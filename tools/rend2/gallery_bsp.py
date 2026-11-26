"""Read-only BSP camera checks for documentation scouting."""
import struct
import zipfile
from pathlib import Path

class World:
    def __init__(self,installation,map_name):
        for archive in sorted((Path(installation)/'base').glob('*.pk3'),key=lambda p:p.name.lower(),reverse=True):
            with zipfile.ZipFile(archive) as pack:
                try:self.data=pack.read('maps/'+map_name+'.bsp')
                except KeyError:continue
            break
        else:raise FileNotFoundError(map_name)
        def lump(index,fmt):
            offset,size=struct.unpack_from('<ii',self.data,8+index*8)
            return list(struct.iter_unpack(fmt,self.data[offset:offset+size]))
        self.planes=lump(2,'<4f');self.nodes=lump(3,'<9i');self.leaves=lump(4,'<12i')
        self.leaf_brushes=lump(6,'<i');self.brushes=lump(8,'<3i');self.sides=lump(9,'<3i')
        self.shaders=lump(1,'<64sii')

    def camera(self,point):
        index=0
        while index>=0:
            node=self.nodes[index];plane=self.planes[node[0]]
            distance=sum(point[i]*plane[i] for i in range(3))-plane[3]
            index=node[1 if distance>=0 else 2]
        leaf=self.leaves[-index-1]
        solid=[]
        for entry in self.leaf_brushes[leaf[10]:leaf[10]+leaf[11]]:
            number=entry[0];first,count,shader=self.brushes[number]
            if not (self.shaders[shader][2]&1):continue
            inside=True
            for side in self.sides[first:first+count]:
                plane=self.planes[side[0]]
                if sum(point[i]*plane[i] for i in range(3))-plane[3]>.01:
                    inside=False;break
            if inside:solid.append(number)
        return dict(cluster=leaf[0],solid_brushes=solid,free=leaf[0]>=0 and not solid)
